#!bin/smash
#
# Demonstrates support for pipelining

echo Executing: du /tmp | sort -nr | wc -l
du /tmp | sort -nr | wc -l

echo Executing: du /tmp | sort -nr | wc -l > wc.out
du /tmp | sort -nr | wc -l > wc.out
echo Executing: cat < wc.out
cat < wc.out
rm wc.out
