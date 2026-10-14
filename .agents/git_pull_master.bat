@echo off
setlocal

git switch master || (pause & exit /b)
git pull --rebase origin master || (pause & exit /b)
