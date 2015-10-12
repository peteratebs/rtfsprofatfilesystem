@echo off
RMDIR /S /Q ..\releasecandidate
MKDIR ..\releasecandidate
echo creating RTFSBASIC
CALL makerelease RTFSBASIC
echo creating RTFSPRO
CALL makerelease RTFSPRO
echo creating RTFSPROFAILSAFE
CALL makerelease RTFSPROFAILSAFE
echo creating RTFSPROPLUS
CALL makerelease RTFSPROPLUS
echo creating RTFSPROPLUSFAILSAFE
CALL makerelease RTFSPROPLUSFAILSAFE
echo creating RTFSPROPLUS64
CALL makerelease RTFSPROPLUS64
echo creating RTFSPROPLUS64FAILSAFE
CALL makerelease RTFSPROPLUS64FAILSAFE
echo creating RTFSPROPLUSDVR
CALL makerelease RTFSPROPLUSDVR
echo creating RTFSPROPLUSDVRFAILSAFE
CALL makerelease RTFSPROPLUSDVRFAILSAFE
