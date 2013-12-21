Here are something necessary to mention:

1.I use vs2010 to develop project2, I run program both in vs,it work well. I also tried to run it in mingw64(my windows is win7x64), it can work but strange error will happened all the time. So I strongly recommand running my program in vs.

2.to compile it, i use the same command as in website
  gcc -g test.c base.c sample.c state_printer.c z502.c -lm -lpthread -o z502
  if doesn't work, use below
  gcc -g test.c base.c sample.c state_printer.c z502.c -lm -o z502
  but I strongly recommand running my program in vs

3.to run my program, just use command like this
  window: Z502.exe test2a
  linux:  ./Z502 test2a

4.I have finished test2a-test2g, but test2d and test2g may have some problems.

5.when run test2c,test2e,test2f,it may occur that "idle loop forever", It is not very often, if it happens, just run one more time. 

6.test2f and test2g cost time, be patient^^, look out of window and have a coffee.