savedcmd_/home/vm/proyecto1/modules/sysinfo.mod := printf '%s\n'   sysinfo.o | awk '!x[$$0]++ { print("/home/vm/proyecto1/modules/"$$0) }' > /home/vm/proyecto1/modules/sysinfo.mod
