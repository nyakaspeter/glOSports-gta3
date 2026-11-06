#!/bin/sh
# Grand Theft Auto III (Rockstar's Android build) on goodluckOS:
# ./gta3 loads the game's own libR1.so from game/ and runs it on ARM's
# Mali driver (glos-gpu). Game files in game/ (README.md). Logs to
# /dev/shm/gta3.log.
cd "$(dirname "$0")"
# Xbox-style positions (A = bottom), as in the other GTA ports.
export SDL_GAMECONTROLLERCONFIG="1900c621474133362d4d422047616d00,GA36-MB Gamepad,a:b0,b:b1,x:b3,y:b2,back:b8,start:b9,guide:b8,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b11,rightstick:b12,dpup:b13,dpdown:b14,dpleft:b15,dpright:b16,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux"
export GAME_STICK_SCALE=1.5     # GA36-MB sticks reach only ~2/3 of the declared range
export GAME_STICK_DEADZONE=0.12 # radial; the sticks rest up to 9% off centre after scaling
export GAME_FPS_COUNTER=1       # frames per second in the top-right corner
exec ./glos/glos-gpu run mali ./gta3 "$@" >/dev/shm/gta3.log 2>&1
