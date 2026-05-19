# coopos — рабочие заметки

Репозиторий: `coopenomics/coopos` (форк Antelope/Leap). Сборка → `dicoop/blockchain` (Docker) + `.deb`. Соседи: `~/docker-hub/` (тест-харнесс миграций/снимков), `~/playbooks/` (Ansible для прод-нод).

## Миграции версий (подтверждено 2026-05-05: 5.1.0 → 5.2.0)

Минорный апгрейд бесшовен при одинаковом toolchain:

```
systemctl stop nodeos
dpkg -i coopos_X.Y.Z-…amd64.deb
systemctl start nodeos
```

Без replay, без двух нод. Перед роллаутом — `~/docker-hub/scripts/test-deb-compat.sh` с актуальными .deb на одном data dir.

**Когда replay реально нужен:** правки в `libraries/chain/include/eosio/chain/*_object.hpp` (поля chainbase) или смена major gcc/boost у CI runner'а. Тогда план — snapshot → clean `data/state*` → start `--snapshot`.

**Подвох legacy-образов:** `dicoop/blockchain:v5.1.0-dev` (апрель 2024, старый Dockerfile с .deb от Leap) с современным `latest` несовместим по shared_memory из-за разного build env, не из-за кода. Не использовать pre-built образы старше нескольких месяцев как baseline миграционного теста.

**Why:** `chainbase` (boost::interprocess managed_mapped_file) сериализует структуры через memcpy memory layout — критично совпадение gcc/boost у двух последовательных бинарников.

## Полная нода ≠ snapshot

При диагностике проблемных нод coopos (api prod, moochest:nodeos) **не предлагать snapshot-restart как путь**, даже если на порядки быстрее. Задача именно полный resync через blocks.log; snapshot скрывает проблемы (OOM, форки, застревания), а не диагностирует их.

**How to apply:** варианты восстановления выбирать из множества `{trim+replay, hard-replay, ручной rebuild reversible/state}`. Snapshot — только если пользователь сейчас прямо просит «возьми снапшот».

**Why:** инцидент 2026-05-18 — при анализе сиротского блока 113,273,321 я предложил A) full replay B) snapshot, получил «Snapshot работает, я знаю. У меня нет задачи со Snapshot запускаться. Задача — полную ноду синхронизировать нормально».

Связанные операционные пометки про nodeos (replay OOM, SIGHUP во время startup) — в `~/playbooks/CLAUDE.md` → раздел EOSIO ops.

## Dockerfile.publish — всегда явный target

В `coopos/.cicd/Dockerfile.publish` стадии: `builder → runtime → deb (FROM scratch)`. У `docker/build-push-action` без `target:` публикуется **последняя** стадия — это `deb` (scratch с .deb), не `runtime`. Результат: `dicoop/blockchain:latest` улетает неработающим scratch-артефактом.

**How to apply:** в `coopos/.github/workflows/docker-publish.yaml` у любого шага `docker/build-push-action`, который пушит runtime-образ, **обязателен** `target: runtime`. Нельзя полагаться на «последняя стадия = runnable image» когда после неё есть scratch-export. Любая правка `Dockerfile.publish` со сменой порядка стадий — перепроверять оба build-push шага в workflow.

**Why:** инцидент 2026-05-07 — закоммитил multi-stage с `deb` в конце, в workflow забыл `target: runtime`; commit 9b6cadb3be7 переключился на сломанный `:latest`.

## Заготовка `~/docker-hub/` (тест-харнесс)

- `scripts/start.sh --image <name> --tag <tag> [--from-snapshot --clean --replay --hard-replay --extra "..." --follow]` — параметризуемый запуск через docker compose.
- `scripts/{stop,status,fetch-snapshot,fetch-debs,build-deb-image}.sh` — операции.
- `scripts/test-compat.sh` — снапшот-совместимость двух тегов в Docker Hub.
- `scripts/test-deb-compat.sh` — миграция .deb на одном data dir (главный тест перед прод-апгрейдом).
- `scripts/fork-snapshot.sh` — снапшот → JSON → JQ-патч → бинарь через `leap-util snapshot to-json/from-json` (subcommand `from-json` добавлен в coopos v5.2.0+, см. `programs/leap-util/actions/snapshot.cpp`).
- `scripts/start-fork.sh` — поднимает локальный writable fork (без p2p, eosio продьюсит сам под dev-key). Подтверждено 2026-05-05: `eosio::updateauth` от dev-key принят форкнутой нодой.
- `patches/dev-fork.jq` — точечный патч под coopos-снапшот (single-producer mode).
- `config-fork/config.ini` — конфиг форкнутой ноды.

**Структура снапшота coopos** (подтверждено 2026-05-05): верхний уровень JSON — секции как ключи (не `.sections[]`), формат `{"eosio::chain::permission_object": {rows: [...], num_rows: N}, ...}`. Ключевые секции для патча: `permission_object`, `block_state.rows[0].active_schedule.producers[*].authority[1].keys`, `block_state.rows[0].valid_block_signing_authority[1].keys`. Coopos в single-producer mode — `eosio` единственный продьюсер.
