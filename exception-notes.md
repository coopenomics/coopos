# chain-exception registry — спецификация реализации

Заметка-набросок к реализации механизма исторических исключений валидации
для coopos. Цель: восстановить штатную возможность full-history синхронизации
(genesis → head) на mainnet Коопеномикс при сохранении чистоты core-кода для
форков и подсетей.

## 1. Контекст инцидента

**Когда:** 11 мая 2026, ~10:18 UTC.

**Что произошло:**
- На BP coopenomics-mainnet был задеплоен бинарь `coopos v5.2.0-dev-294edf3b8`
  (dev-сборка, а не финальная `v5.2.0-2c23b810`).
- В этой dev-сборке не был зарегистрирован `on_activation` handler для
  protocol feature `ASSERT_RECOVER_KEY_ACCOUNT` (id=24). Фикс пришёл коммитом
  `2c23b8108 fix(protocol_feature): register on_activation handler for ASSERT_RECOVER_KEY_ACCOUNT`,
  но он попал только в финальную сборку.
- На каждом блоке implicit `onblock` action не регистрировала intrinsics,
  `_action_receipt_digests` оставалась пустой, `action_mroot` финализированного
  блока вычислялся как нулевой digest.
- Через ~30 минут (~3000 блоков) что-то самостоятельно активировало feature
  (видимо preactivation tx), и `action_mroot` снова стал не-нулевым.

**Дефектное окно блоков:** `[113 273 322 .. 113 275 716]` — 2395 блоков, ~20 минут.
Границы подтверждены `leap-util block-log print-log` по blocks.log BP 2026-06-03:
блок `113 273 321` — последний с не-нулевым `action_mroot` (нормальный),
`113 275 717` — первый с не-нулевым после самовосстановления.

**Содержимое окна:** в выборках из 11 блоков `trxs=0`. Пользовательских
транзакций не было — только пустые `onblock`. Утрачена только криптографическая
верифицируемость `action_mroot` для этих блоков, но не данные.

**Последствия для full replay:**
- Эти блоки **навсегда** в blocks.log с `action_mroot = 0`.
- Любой текущий бинарь coopos (включая финальный `v5.2.0-2c23b810`) при
  apply_block вычисляет ожидаемый не-нулевой `action_mroot` и отвергает блок
  113 273 322 через `block_validate_exception` в `controller.cpp` (текущая
  строка ~2154, ассерт `producer_block_id == ab._id`).
- Любая попытка полного p2p sync от genesis или `--hard-replay-blockchain`
  на текущий момент **невозможна** — даже на BP с собственным blocks.log.

**chain_id mainnet Коопеномикс:** `6e37f9ac0f0ea717bfdbf57d1dd5d7f0e2d773227d9659a63bbf86eec0326c1b`
(сверено `curl https://api.coopenomics.world/v1/chain/get_info` 2026-06-03).

## 2. Цели

1. **Восстановить full-history sync** как штатную операционную возможность
   для mainnet Коопеномикс (новые архивные ноды, новые BP, аудиторы).
2. **Не загрязнять core-код** магическими константами с block_num конкретной
   сети — coopos тиражируется в форки и подсети, они не должны наследовать
   эту легаси-логику.
3. **Не создавать поверхность для подделки блоков** через bypass — bypass
   должен срабатывать только на конкретное окно конкретной цепи и только при
   ровно том mismatch, который наблюдается (action_mroot = 0, всё остальное
   совпадает).

## 3. Архитектурный подход — chain-exception registry

Паттерн заимствован из go-ethereum (`ChainConfig` с `DAOForkBlock` и
аналогичными hardfork-полями). Core имеет **generic механизм**, конкретные
данные живут в **отдельном файле per chain_id**.

**Контракт:**
- Файла нет → `_historical_exceptions` пустой → bypass-код мёртв → core ведёт
  себя строго как апстрим Antelope.
- Файл есть, но `chain_id` в нём не равен текущему `self.get_chain_id()` →
  файл отвергается с ошибкой на startup. Невозможно «подсунуть» окна другой
  сети.
- Файл есть и `chain_id` совпадает → объявленные окна разрешены к bypass.

## 4. Структуры данных

Новый заголовок `libraries/chain/include/eosio/chain/chain_exceptions.hpp`:

```cpp
namespace eosio { namespace chain {

struct historical_action_mroot_window {
   uint32_t    from_block = 0;
   uint32_t    to_block   = 0;
   std::string reason;          // для логов и forensics
};

struct chain_historical_exceptions {
   chain_id_type chain_id;
   std::vector<historical_action_mroot_window> action_mroot_zero_windows;
};

}}

FC_REFLECT(eosio::chain::historical_action_mroot_window,
           (from_block)(to_block)(reason))
FC_REFLECT(eosio::chain::chain_historical_exceptions,
           (chain_id)(action_mroot_zero_windows))
```

## 5. Загрузка файла

**Опция config.ini** (новая):

```ini
chain-historical-exceptions = /etc/coopos/exceptions/coopenomics-mainnet.json
```

**Загрузчик** в `controller_impl::startup()` (или ранее, до первого
`apply_block`):

```cpp
void load_historical_exceptions( const fc::path& p ) {
   if( p.empty() || !fc::exists(p) ) return;     // no file → strict mode
   auto ex = fc::json::from_file(p).as<chain_historical_exceptions>();
   EOS_ASSERT( ex.chain_id == self.get_chain_id(),
               chain_exception,
               "historical exceptions file chain_id ${ex} does not match running chain ${rc}",
               ("ex", ex.chain_id)("rc", self.get_chain_id()) );
   _historical_exceptions = std::move(ex);
   for( const auto& w : _historical_exceptions.action_mroot_zero_windows ) {
      wlog("Loaded historical action_mroot_zero window [${a}..${b}]: ${r}",
           ("a", w.from_block)("b", w.to_block)("r", w.reason));
   }
}
```

## 6. Точка применения в validation

`libraries/chain/controller.cpp`, в `apply_block()` (текущая строка ~2153,
блок `if( producer_block_id != ab._id )`):

```cpp
if( producer_block_id != ab._id ) {
   const uint32_t bn = b->block_num();
   bool window_bypass = false;
   for( const auto& w : _historical_exceptions.action_mroot_zero_windows ) {
      if( bn < w.from_block || bn > w.to_block ) continue;
      if( b->action_mroot != digest_type() ) continue;          // ровно нулевой
      if( !other_header_fields_match(*b, *ab._unsigned_block) ) continue;
      wlog("historical action_mroot_zero exception applied for block ${bn}: ${r}",
           ("bn", bn)("r", w.reason));
      window_bypass = true;
      break;
   }
   if( !window_bypass ) {
      elog("Validation block id does not match producer block id");
      report_block_header_diff(*b, *ab._unsigned_block);
      EOS_ASSERT( producer_block_id == ab._id, block_validate_exception,
                  "Block ID does not match",
                  ("producer_block_id", producer_block_id)("validator_block_id", ab._id) );
   }
}
```

`other_header_fields_match()` — новый помощник в том же файле, проверяющий
равенство всех полей header'а **кроме** `action_mroot`:

```cpp
static bool other_header_fields_match( const block_header& b,
                                       const block_header& ab ) {
   return b.timestamp          == ab.timestamp
       && b.producer           == ab.producer
       && b.confirmed          == ab.confirmed
       && b.previous           == ab.previous
       && b.transaction_mroot  == ab.transaction_mroot
       && b.schedule_version   == ab.schedule_version
       && b.new_producers      == ab.new_producers
       && b.header_extensions  == ab.header_extensions;
}
```

Это гарантирует: bypass срабатывает строго на тот mismatch, который мы
понимаем (action_mroot обнулён). Любой другой расход — обычный фейл, никакой
амнистии.

## 7. Файл данных для mainnet Коопеномикс

Путь (рекомендуется): `/etc/coopos/exceptions/coopenomics-mainnet.json`.
В deb-пакет `coopos-mainnet-config` (опциональный) или поставляется отдельно
оператором.

```json
{
  "chain_id": "6e37f9ac0f0ea717bfdbf57d1dd5d7f0e2d773227d9659a63bbf86eec0326c1b",
  "action_mroot_zero_windows": [
    {
      "from_block": 113273322,
      "to_block": 113275716,
      "reason": "v5.2.0-dev-294edf3b8 deployed on mainnet 2026-05-11 10:18 UTC; ASSERT_RECOVER_KEY_ACCOUNT (id=24) on_activation handler was not registered; ~20min (2395 blocks) of zero action_mroot before self-recovery; user trxs in window: 0; fix landed in coopos 2c23b8108"
    }
  ]
}
```

Финальный артефакт лежит в `~/playbooks/blockchain/exceptions/coopenomics-mainnet.json`.
На целевой ноде разворачивается по пути `/etc/coopos/exceptions/coopenomics-mainnet.json`,
путь указывается в `config.ini` опцией `chain-historical-exceptions`.

## 8. Свойства решения

**Форки/подсети coopos**:
- Запускаются без `chain-historical-exceptions` или с пустым путём.
- `_historical_exceptions` пустой → цикл проверки `for(... windows ...)` не
  итерирует → bypass-код является мёртвой веткой.
- Никаких magic block_num в коде — только generic-механизм.

**Перенос на другую сеть невозможен**:
- Скопировать файл `coopenomics-mainnet.json` в другую сеть бессмысленно —
  при startup проверка `ex.chain_id == self.get_chain_id()` фейлится,
  nodeos падает на старте с понятной ошибкой.

**Подделка блоков в окне невозможна**:
- Bypass принимает блок только если он действительно от producer'а
  (валидная подпись, корректный `previous`-link к предыдущему irreversible
  блоку, все остальные поля как у assembled — кроме action_mroot=0).
- Все block_id в окне уже **irreversible** (LIB сейчас ~114 800 000+,
  окно на 1.5M блоков позади). Подменить irreversible block_id невозможно
  без collision на SHA256 и без приватного ключа producer'а на момент
  11.05.2026.

**Криптографическая честность**:
- Bypass прозрачен — каждое применение пишется в лог через `wlog` с reason.
- Файл exceptions можно подписать BP-multisig'ом и распространять подписанным,
  если в будущем потребуется усилить аудит (не в первой итерации).

## 9. Тестовое покрытие

Минимально:

1. **Нет файла → строгая валидация.** Unit-тест: nodeos с искусственно
   обнулённым action_mroot на синтетическом блоке без exceptions → ассерт
   валится.
2. **Файл есть, chain_id не совпадает → отказ на startup.** Тест с подменой
   chain_id в exceptions-файле → ошибка загрузки.
3. **Файл есть, окно совпадает, mismatch только в action_mroot=0 → bypass
   применён.** Replay через искусственный blocks.log с обнулённым блоком в
   объявленном окне → нода продолжает.
4. **Файл есть, окно совпадает, mismatch в другом поле тоже → fail.** Не
   только action_mroot обнулён, но и transaction_mroot искажён → ассерт
   валится (защита от использования bypass-а как универсальной амнистии).
5. **Файл есть, окно НЕ совпадает (block_num вне диапазона) → fail.** Блок
   с обнулённым mroot за пределами объявленного окна → ассерт валится.

Тесты лежат в `tests/chain_tests/` (или новом `tests/historical_exceptions_tests/`).

## 10. План внедрения

1. **Уточнить верхнюю границу окна** на blocks.log BP (скрипт через
   `leap-util block-log print-log --first 113273322 --last 113280000` + grep
   по action_mroot).
2. **Получить mainnet chain_id** через `get_info`.
3. **Реализовать registry** (структуры, загрузчик, точка применения в
   `apply_block`) и тесты — отдельный feature branch `chain-exception-registry`.
4. **Подготовить data-файл** `coopenomics-mainnet.json` с точными значениями.
5. **CI**: убедиться что upstream-тесты Antelope проходят без изменений
   (нет файла → нет регрессии поведения).
6. **Roll-out**: новый минор coopos (например `v5.3.0`) с registry. BP и
   API-ноды mainnet кладут data-файл и указывают путь в config.ini.
7. **Архивная нода**: на свежей машине `nodeos --hard-replay-blockchain` с
   v5.3.0 + data-файл → должна пройти через окно и достичь head'а.

## 11. Связанные документы

- `~/.claude/projects/-home-admin/memory/reference_coopos_v520_dirty_window.md` —
  исходный диагноз инцидента и его последствия.
- `~/.claude/projects/-home-admin/memory/reference_eosio_replay_eos_vm_oc.md` —
  отдельный фикс OOM при replay (`eos-vm-oc-enable = none`).
- `~/.claude/projects/-home-admin/memory/reference_eosio_sighup_startup_bug.md` —
  баг appbase с SIGHUP во время startup.
- `coopos/CLAUDE.md` — раздел про миграции версий и Dockerfile.publish.
