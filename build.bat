@echo off
REM ================================================================
REM  TaskForge v2 Build Script
REM  Usage: build.bat [v2|v1|gui|all|clean]
REM
REM  v2   = Banking System on TaskForge OS (main project)
REM  v1   = Original OS simulator (legacy)
REM  gui  = Win32 GUI version
REM  all  = Build everything
REM ================================================================

set PATH=C:\msys64\mingw64\bin;%PATH%
set CC=gcc
set LDFLAGS=-lpthread -static

if "%1"=="clean" goto clean
if "%1"=="v1"    goto v1
if "%1"=="gui"   goto gui
if "%1"=="v2"    goto v2
if "%1"=="all"   goto all
goto v2

:all
echo.
echo [TaskForge] Building ALL targets...
echo.
call :v2
call :v1
call :gui
goto done

:v2
echo ============================================================
echo  Building TaskForge v2 (Banking System on OS Kernel)
echo ============================================================
if not exist obj mkdir obj
echo [v2] Compiling OS Kernel...
%CC% -Wall -Wextra -std=c11 -Ikernel -c kernel/os_kernel.c -o obj/os_kernel.o 2>&1 && echo   os_kernel.c OK
echo [v2] Compiling Banking Application...
%CC% -Wall -Wextra -std=c11 -Ikernel -c app/bank.c -o obj/bank.o 2>&1 && echo   bank.c OK
echo [v2] Compiling Main...
%CC% -Wall -Wextra -std=c11 -Ikernel -c main_v2.c -o obj/main_v2.o 2>&1 && echo   main_v2.c OK
echo [v2] Linking...
%CC% -static -o taskforge_v2.exe obj/main_v2.o obj/os_kernel.o obj/bank.o %LDFLAGS%
if %ERRORLEVEL% EQU 0 (
    echo [v2] Build successful: taskforge_v2.exe
) else (
    echo [v2] Build FAILED
)
echo.
goto :eof

:v1
echo ============================================================
echo  Building TaskForge v1 (OS Simulator - Legacy)
echo ============================================================
if not exist obj mkdir obj
%CC% -Wall -Wextra -std=c11 -Iinclude -c main.c -o obj/main.o 2>&1 && echo   main.c OK
%CC% -Wall -Wextra -std=c11 -Iinclude -c src/process_mgmt.c -o obj/process_mgmt.o 2>&1 && echo   process_mgmt.c OK
%CC% -Wall -Wextra -std=c11 -Iinclude -c src/scheduler.c -o obj/scheduler.o 2>&1 && echo   scheduler.c OK
%CC% -Wall -Wextra -std=c11 -Iinclude -c src/deadlock.c -o obj/deadlock.o 2>&1 && echo   deadlock.c OK
%CC% -Wall -Wextra -std=c11 -Iinclude -c src/memory_mgmt.c -o obj/memory_mgmt.o 2>&1 && echo   memory_mgmt.c OK
%CC% -Wall -Wextra -std=c11 -Iinclude -c src/io_file_mgmt.c -o obj/io_file_mgmt.o 2>&1 && echo   io_file_mgmt.c OK
%CC% -Wall -Wextra -std=c11 -Iinclude -c src/task_ops.c -o obj/task_ops.o 2>&1 && echo   task_ops.c OK
echo [v1] Linking...
%CC% -static -o taskforge.exe obj/main.o obj/process_mgmt.o obj/scheduler.o obj/deadlock.o obj/memory_mgmt.o obj/io_file_mgmt.o obj/task_ops.o %LDFLAGS%
if %ERRORLEVEL% EQU 0 (
    echo [v1] Build successful: taskforge.exe
) else (
    echo [v1] Build FAILED
)
echo.
goto :eof

:gui
echo ============================================================
echo  Building TaskForge GUI
echo ============================================================
if exist gui\taskforge_gui.c (
    %CC% -Wall -Wextra -std=c11 -o taskforge_gui.exe gui/taskforge_gui.c -lcomctl32 -lgdi32 -lcomdlg32 -mwindows -static
    if %ERRORLEVEL% EQU 0 (
        echo [GUI] Build successful: taskforge_gui.exe
    ) else (
        echo [GUI] Build FAILED
    )
) else (
    echo [GUI] gui/taskforge_gui.c not found, skipping.
)
echo.
goto :eof

:clean
echo [Clean] Removing build artifacts...
if exist obj rmdir /s /q obj
if exist taskforge.exe del taskforge.exe
if exist taskforge_v2.exe del taskforge_v2.exe
if exist taskforge_gui.exe del taskforge_gui.exe
echo [Clean] Done.
goto done

:done
echo.
echo Build complete.
pause
