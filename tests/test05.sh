#!bin/smash
#
# Demonstrates syntax support for fg/bg jobs

echo Sleeping 5s in fg
sleep 5
echo Sleeping 5s in fg
sleep 5
echo
echo NOTE: Background jobs don't make sense in shell scripts, so we
echo will actually be running everything as a fg job.
echo This is just to prove the syntax works.
echo
echo Sleeping 5s in bg
sleep 5&
echo Sleeping 5s in bg
sleep 5 &
