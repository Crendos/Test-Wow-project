# data/

## arctium_cert_bundle.bin

Подписанный cert-bundle для слота 32761 байт в `.rdata` клиента (конверт
`{"Created":…`) — ровно тот, который в память клиента подставляет
[Arctium WoW Launcher](https://github.com/brian8544/Arctium-Launcher)
(`src/Patches/Common.cs`, `CertBundleData`, MIT).

* Содержимое: JSON (`Created`, `Certificates` (2), `PublicKeys`,
  `SigningCertificates` (3)) + padding до ровно **32761** байта.
* Содержит цепочки «Arctium Sandbox / TrinityCore / CypherCore» — то есть
  сертификаты вашего auth/bnet-сервера, выданные этими CA, клиент примет
  без установки в системный trust store.

Применение (CLI):

```bash
wow_patch_cli patch Wow.exe --portal 127.0.0.1:1119 --cert-file data\arctium_cert_bundle.bin
```

Источник: https://github.com/brian8544/Arctium-Launcher (MIT).
Если ваш сервер использует обычный самоподписанный сертификат, который не
цепляется к этим CA — вместо bundle действует режим dev/системный trust
store (см. README лаунчера).
