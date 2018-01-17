#!/bin/sh

continued="yes"

while [ "${continued}" == "yes" ]
do
  testcase[0]="consoleIO_test1"
  testcase[1]="consoleIO_test2"
  testcase[2]="consoleIO_test3"
  testcase[3]="end"
  priority[0]=0
  priority[1]=0
  priority[2]=0

  num=0

  while [ "${continued}" == "yes" ]
  do
     read -p "Type a file name or end: " testcase[${num}]
    
     if [ "${testcase[${num}]}" == "end" ]; then
       break
     fi

     read -p "Type its priority: " priority[${num}]

     num=$((num + 1))

  done
  
  if [ "${num}" == "1" ]; then
    ../build.linux/nachos -ep ${testcase[$((num-1))]} ${priority[$((num-1))]}
  elif [ "${num}" == "2" ]; then
    ../build.linux/nachos -ep ${testcase[$((num-2))]} ${priority[$((num-2))]} -ep ${testcase[$((num-1))]} ${priority[$((num-1))]}
  elif [ "${num}" == "3" ]; then
    ../build.linux/nachos -ep ${testcase[$((num-3))]} ${priority[$((num-3))]} -ep ${testcase[$((num-2))]} ${priority[$((num-2))]} -ep ${testcase[$((num-1))]} ${priority[$((num-1))]} 
  fi

  printf "\n"
  read -p "Continue or not (yes/no): " continued

done
