setlocal
pushd "%~dp0" || exit /b 1

del /s /q .ccache
del /s /q dist
call build_docker.bat

set "exit_code=%errorlevel%"
popd
exit /b %exit_code%