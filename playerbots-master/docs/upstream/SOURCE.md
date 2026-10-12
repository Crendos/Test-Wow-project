# Откуда взят MidnightBotAI

Апстрим-проект: **psychochaingang/LrdPsychoChains-WOW-HUB**, путь `projects/midnightbotai`
(публичный, ветка `main`; прежнее зеркало `github.laiyagushi.com` больше не работает).

- Единственный коммит апстрима по проекту: `7ba608f2` от **2026-09-27**
  («add MidnightBotAI project (readme, plain-language changelog, source)»).
- Наше слияние (`eeb1bb6`/`daa7a14`) сделано ПОЗЖЕ — весь `source/` влит актуальным:
  `src/bot/MidnightBotAI.cpp`, `src/bot/MidnightBotMgr.cpp/.h`, `conf/MidnightBotAI.conf.dist`.
- Их `midnight_bot_ai_loader.cpp` не нужен: регистрация идёт через наш
  `AddSC_playerbots() → AddSC_midnight_bot_ai()`.
- Здесь сохранены два файла, которых не было в нашем репо:
  `MidnightBotAI_CHANGELOG.md` (история версий 1.x–4.8) и `MidnightBotAI_README.md`.
