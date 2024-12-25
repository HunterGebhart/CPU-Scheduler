CPUscheduler: CPUscheduler.c
	gcc -o CPUscheduler CPUscheduler.c -pthread
clean:
	rm *.o *.class CPUscheduler