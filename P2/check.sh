#!/bin/bash
#Javier Cupeiro Rodríguez
#Jaime Sáez de Buruaga
#Para el correcto funcionamiento es necesario que se ejecute este script dentro de la carpeta
#FUSE_myFS con el sistema de archivos ya montado sobre la ruta mount-point...

echo "__________________________________________"
echo "Iniciando prueba sobre sistema de ficheros"
echo "Pulse enter..."
read p

echo "--------------------------------------------------"
echo "Iniciando pruebas sobre archivos regulares"

echo "__________________________________________________"
echo "A) Creando archivo regular test.txt con touch"
echo "Probando funcion crear" > mount-point/test.txt
echo "Pulse enter..."
read p

echo "__________________________________________________"
echo "B) Escribiendo sobre archivo regular con echo..."
echo "Probando funcion write" > mount-point/test.txt 
echo "Pulse enter..."
read p

echo "__________________________________________________"
echo "C) Leyendo archivo regular con cat..."
cat mount-point/test.txt
echo "Pulse enter..."
read p


echo "__________________________________________________"
echo "D) Eliminando archivo regular con rm..."
rm mount-point/test.txt
echo "Pulse enter..."
read p

echo "--------------------------------------------------"
echo "Iniciando pruebas sobre directorio"
echo "__________________________________________________"
echo "Creando directorio..."
mkdir mount-point/tester/
ls -la mount-point/
echo "Pulse enter..."
read p

echo "__________________________________________________"
echo "Todas las pruebas completadas con exito!"
echo "Pulse ENTER para salir..."
read salir

exit 0

