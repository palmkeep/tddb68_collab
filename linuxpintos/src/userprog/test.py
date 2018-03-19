import subprocess 
import time


file = open("test_result.txt","wt")
for i in range(50):
    subprocess.check_output(['make','clean'])
    res = subprocess.check_output(['make','check'])
    for j in reversed(xrange(71)):
        info = res.rsplit('\n')[-j]
        file.write(info+ '\n')

file.close()
