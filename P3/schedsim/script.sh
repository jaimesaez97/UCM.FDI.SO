#!/bin/bash

listaDeSchedulers=("RR" "SJF" "FCFS" "PRIO" "TJF")

echo 'Que fichero de ejemplo desea simular? : '
read fichero

while [ ! -f $fichero ]; do
	echo -e "Error: El fichero no existe"
	echo 'Que fichero de ejemplo desea simular? : '
	read fichero
done


echo 'Numero maximo de CPUs que desea usar en la simulacion: '
read maxCPUs

while [ $maxCPUs -gt 8 ] || [ $maxCPUs -lt 1 ]; do

	echo -e "Error: El numero minimo es 1 y el maximo 8"
	echo 'Numero maximo de CPUs que desea usar en la simulacion: '
	read maxCPUs
done

if [ -d resultados ]; then
rm -r resultados
fi
mkdir resultados

for nameSched in "${listaDeSchedulers[@]}"; do
	for (( i = 1 ; $i <= $maxCPUs ; i++ )) do
		./schedsim -n "$i" -s "$nameSched" -i "$fichero"
		
		for (( j = 0 ; $j < $i ; j++ )) do
			mv CPU_$j.log resultados/"$nameSched"-CPU-$j.log
		done
	done

	
done

for nameSched in "${listaDeSchedulers[@]}"; do

	cd ../gantt-gplot

	for (( i = 0 ; $i < $maxCPUs ; i++ )) do
		./generate_gantt_chart ../schedsim/resultados/"$nameSched"-CPU-"$i".log

	done
	
	cd ../schedsim
done













