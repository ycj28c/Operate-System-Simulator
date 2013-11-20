Here are something necessary to mention:

1.I use vs2010 to develop project1, I run program both in vs and MINGW64(my windows is win7x64),it work well. I also tried to run it in ccc linux, well, it can work but as you know, strange error will happened all the time. So I strongly recommand runn my program in vs or MINGW64 gcc.

2.to compile it, i use the same command as in website
  gcc -g test.c base.c sample.c state_printer.c z502.c -lm -lpthread -o z502
  if you are intersting in vs, I can also support vs files.

3.to run my program, just use command like this
  window: Z502.exe test1k
  linux:  ./Z502 test1j

4.I have finished test0-test1k, they work well. To show the feather, I also write the test1m, so please use the test.c in my zip packet. Since the test1l is not scored, so I didn't do it.

5.All my scheduler print output is after operation, it means my sp print show the result of operation.

6.my PCB limit is 15,so in test1b, if the program show"too many(maore than 10) PIDS", its fine.

7.when run test1c,test1d,test1f,it may occur that "idle loop forever", i know that's because the lock things. However, we can not lock the idle loop, so this problem can not avoid. I suggest using VS to run program, then everything will fine. 

