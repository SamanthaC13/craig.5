This program uses make to compile. The executeble to run assignment 5 is ./oss. This program has two options h and v, h is the option fo help and v is the option for verbose. 
The biggest struggle I had with this assignment was the dealock detection algorithm. Currently this program spawns only 5 childern at the same time and has only 10 reasources available.
This is used to show that the program runs into deadlocks and recovers from them. The program also terminates after 15 processes are completed or the timer interrupt occurs in order for
the program to run for a reasonable amount of time. 
