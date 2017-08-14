#!/usr/bin/python2.7
import os, subprocess, select, time



def test_hello(output):
	return "hello, world" in output

def test_spawnhello(o):
	return "i am parent environment" in o and "hello, world" in o

def test_pipe(o):
	return all(s in o for s in [
		"forking",
		"writing to fd..",
		"OK, wrote.",
		"got: 'hello, world!'"
	])

def test_forktree(o):
	return all(s in o for s in [
		": I am '0'",
		": I am '1'",
		": I am '10'",
		": I am '00'",
		": I am '011'",
		": I am '111'",
	])

def test_myfork(o):
	return all(s in o for s in [
		"I am the parent before A",
		"I am the parent after A",
		"I am A",
		"I am the parent after B",
		"I am B",
	])

def test_pingpong(o):
	return all(s in o for s in 
		[" got %d from " % i for i in range(11)])

def test_myipc(o):
	return "parent is OK" in o and "child is OK" in o

tests_table = [
	
	("myipc", test_myipc),
	("hello", test_hello),
	("spawnhello", test_spawnhello),
	("pipe", test_pipe),
	("forktree", test_forktree),
	("myfork", test_myfork),
	("pingpong", test_pingpong),

]







################################################

RED = "\033[0;31m"
GREEN = "\033[0;32m"
NC = "\033[0m"

def green(text):
	return GREEN + text + NC

def red(text):
	return RED + text + NC

def make_qemu_proc(progname):
	cmd = ["make", "run-%s-nox" % progname]
	return subprocess.Popen(cmd, stdout=subprocess.PIPE,
								 stderr=subprocess.STDOUT,
								 stdin=subprocess.PIPE)

def has_data(f):
	r, w, e = select.select([f], [], [], 0)
	return f in r

def run_test(progname, validate, timeout=5):
	proc = make_qemu_proc(progname)
	output = ""
	begin_time = time.time()
	while True:
		if validate(output):
			proc.terminate()
			return True

		spent_time = time.time() - begin_time
		remaining_time = timeout - spent_time
		if remaining_time <= 0:
			proc.terminate()
			return False

		if has_data(proc.stdout):
			output += os.read(proc.stdout.fileno(), 4096)
			continue

		time.sleep(min(0.5, remaining_time))

def report_success(progname):
	print(green("[+] test success: '%s'" % progname))

def report_failure(progname):
	print(red("[-] test failure: '%s'" % progname))

def run_tests(tests):
	successes = 0
	failures = 0
	
	for progname, validate in tests:
		if run_test(progname, validate):
			report_success(progname)
			successes += 1
		else:
			report_failure(progname)
			failures += 1
	
	print "---------------------"
	print "%d tests succeeded" % successes
	print "%d tests failed" % failures

def main():
	os.system("killall qemu-system-i386 2>/dev/null")
	run_tests(tests_table)

if __name__ == "__main__":
	main()
