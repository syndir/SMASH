#!bin/smash
#
# Demonstrates support for wildcard and tilde replacement

echo Doing: cd /
cd /
echo Doing: pwd
pwd
echo Doing: cd ~
cd ~
echo Doing: pwd
pwd

echo Doing: cd /usr/include
cd /usr/include
echo Doing: pwd
pwd
echo Doing: ls *io.h
ls *io.h
