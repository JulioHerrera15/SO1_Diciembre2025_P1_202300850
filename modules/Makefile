# Aquí listamos los archivos objeto (.o) que queremos generar.
# Make buscará automáticamente los archivos .c con el mismo nombre.
obj-m += sysinfo.o
obj-m += continfo.o

# Apunta al directorio de construcción del kernel actual
KDIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(shell pwd) modules

clean:
	make -C $(KDIR) M=$(shell pwd) clean