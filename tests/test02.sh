#!bin/smash
# 
# Demonstrates support for io redirection

echo *** Redirecting standard output to a file and displaying it
/bin/echo $HOME > test_echo_stdout.out
cat < test_echo_stdout.out
rm test_echo_stdout.out

echo *** Redirecting standard error to a file and displaying it
du /home 2> test_du_stderr.err
cat < test_du_stderr.err
rm test_du_stderr.err

echo *** Redirecting standard input from a file
ls > test_ls.in
sort -nr < test_ls.in

echo *** Appending output to an existing file
ls >> test_ls.in
cat < test_ls.in

echo *** Appending output to a non-existant file
ls >> test_ls2.in
cat < test_ls2.in

echo *** Redirecting all streams
sort -nr < test_ls.in > test_sort.out 2> test_sort.err
echo *** test_ls.in contains:
cat < test_ls.in
echo *** test_sort.out contains:
cat < test_sort.out
echo *** test_sort.err contains:
cat < test_sort.err

rm test_ls.in test_sort.out test_sort.err test_ls2.in
