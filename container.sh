#!/bin/bash

# Nombre base
BASE_NAME="so1_cont"

# Crear 10 contenedores
for i in {1..10}
do
    # Generar aleatorio entre 1 y 3
    TYPE=$(( ( RANDOM % 3 ) + 1 ))
    
    # ID aleatorio para el nombre
    RANDOM_ID=$(( RANDOM % 10000 ))
    NAME="${BASE_NAME}_${RANDOM_ID}"

    if [ $TYPE -eq 1 ]; then
        # Opción 1: Bajo Consumo
        echo "Creando contenedor BAJO CONSUMO: $NAME"
        docker run -d --name "$NAME" img_bajo

    elif [ $TYPE -eq 2 ]; then
        # Opción 2: Alto Consumo CPU
        echo "Creando contenedor ALTO CPU: $NAME"
        docker run -d --name "$NAME" img_alto_1

    else
        # Opción 3: Alto Consumo RAM
        echo "Creando contenedor ALTO RAM: $NAME"
        docker run -d --name "$NAME" img_alto_2
    fi
done

echo "Ciclo de creación finalizado."