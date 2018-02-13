#!/bin/bash

listaDeSchedulers="RR SJF FCFS PRIO"

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


for nameSched in $listaDeSchedulers
do
start=1
start2=$maxCPUs
for ((cpus=start; cpus<=start2; cpus++))

	do
	./schedsim -n "$cpus" -s "$nameSched" -i "$fichero"
		
		
		for i in $(seq 0 $cpus)
		do
			mv CPU_$i.log resultados/"$nameSched"-CPU-$i.log
		done
	done
done

for nameSched in $listaDeSchedulers
do

	cd ../gantt-gplot
	
	for cpus in $(seq 0 $maxCPUs)
	do
		./generate_gantt_chart ../schedsim/resultados/"$nameSched"-CPU-"$cpus".log

	done
	
	cd ../schedsim
done













