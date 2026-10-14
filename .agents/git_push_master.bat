@echo off
setlocal

git switch master || (pause & exit /b)
git push origin master || (pause & exit /b)
