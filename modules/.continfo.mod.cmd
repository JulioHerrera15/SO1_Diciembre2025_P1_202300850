savedcmd_/home/vm/proyecto1/modules/continfo.mod := printf '%s\n'   continfo.o | awk '!x[$$0]++ { print("/home/vm/proyecto1/modules/"$$0) }' > /home/vm/proyecto1/modules/continfo.mod
