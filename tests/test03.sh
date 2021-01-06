#!bin/smash
#
# Demonstrates support for environment variables and
# $? (exit code of last compeleted foreground job)

echo a $HOME b
echo $HOME
echo $HOME and $PATH

cd /
pwd
cd $HOME
pwd

ls -l
echo Last job exited with status $?

du /askjdklasjdlkasdj
echo Last job exited with status $?
