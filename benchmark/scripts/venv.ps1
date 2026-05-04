python setupvenv.py

$activatePath = ".\venv\Scripts\Activate.ps1"
if (Test-Path $activatePath) {
    Write-Host "--- Activating Virtual Environment ---" -ForegroundColor Cyan
    
    & $activatePath
    
    Write-Host "Success! Your shell is now using the venv." -ForegroundColor Green
} else {
    Write-Error "Could not find the activation script at $activatePath"
}