Nico Lohner 811775576 Jake Schramm 811322582

to compile: run 'make'

to run: run './1730sh'

TODO:
	bookkeeping (I'm pretty sure done? not confirmed)
	fg builtin (done if the job is stopped before it executes, I think I'll leave it like this, I was getting really weird
		    functionality when I had background jobs running {DOES NOT WORK IF JOB IS STOPPED AFTER EXECUTING})
	bg builtin (I don't really have a way to test if this works, but I was getting the same weird functionality
		    that I was getting earlier, so I assume it works?)
	kill builtin (done for some specific signals, I don't know how many we need to add)
	
	done with export builtin but remember to add "builtin = true" to it
	after we merge our .cpp files 
