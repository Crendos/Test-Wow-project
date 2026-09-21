$ErrorActionPreference = "Stop"
Write-Host "Installing wowdata from npm..."
npm install -g @follenfang/wowdata
Write-Host ""
Write-Host "Checking installation..."
wowdata --version
wowdata doctor
Write-Host ""
Write-Host "Done. Restart WoW DB Studio so it can discover wowdata in PATH."
