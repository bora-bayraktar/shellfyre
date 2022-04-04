# Shellfyre
Shell project developed for **Koç University Operating Systems** course with various custom shell commands and a kernel module for **_Linux_** based systems.

### Installation
Use provided **Makefile**. 
- Type ```make```, provided **Makefile** will compile and run the program.
- Enter your ```sudo``` password. The program must be executed with ```sudo``` permissions, so that it can insert the kernel module.

### Uninstallation
The kernel module is removed when you exit the shell. To clean the executable files, use provided **Makefile**.
- Type ```make clean```.
