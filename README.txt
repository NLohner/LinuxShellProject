Nico Lohner 811775576 Jake Schramm 811322582

to compile: run 'make'

to run: run './1730sh'

notes:
	running a program with & will NOT run it in the background, it will stop just before executing the program.
	In this case, using fg will start the program in the foreground, and using bg results in really odd behaviour
	that I don't understand. The kill builtin only works for specific signals, and It's pretty clear in the code
	what signals work.
