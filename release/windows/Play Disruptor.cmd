@echo off
setlocal
pushd "%~dp0"
if not exist "input\Disruptor (USA).cue" (
    echo Copy your Disruptor USA BIN/CUE files into the input folder.
    echo Name the cue file: "Disruptor (USA).cue"
    echo See GETTING_STARTED.md for setup instructions.
    pause
    popd
    exit /b 1
)
"DisruptorRecompiled.exe" --no-launcher --game "game.toml" --disc "input\Disruptor (USA).cue" > "startup.log" 2>&1
set "game_result=%errorlevel%"
if not "%game_result%"=="0" (
    echo Disruptor exited with an error. See startup.log and GETTING_STARTED.md.
    pause
)
popd
exit /b %game_result%
