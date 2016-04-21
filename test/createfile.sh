#!/bin/sh

i=$1
while [ $i -lt $2 ] ;
do
     if [ `ps -ef | grep awk | wc -l` -gt 10 ] ;
     then
            sleep 5
            continue
     else
            if [ $i -lt 10 ] ;
            then
                 awk -F'|' -v X=${i}00000000 '{printf "00%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n",X+NR,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16}' gprs_cdr_yyyymmdd.txt > gprs_cdr_20140430_00${i}.txt &
                 sleep 1
                 let i=$i+1
                 continue
            fi
            if [ $i -ge 10 -a $i -lt 100 ] ;
            then
                 awk -F'|' -v X=${i}00000000 '{printf "0%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n",X+NR,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16}' gprs_cdr_yyyymmdd.txt > gprs_cdr_20140430_0${i}.txt &
            else
                 awk -F'|' -v X=${i}00000000 '{printf "%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n",X+NR,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16}' gprs_cdr_yyyymmdd.txt > gprs_cdr_20140430_${i}.txt &
            fi
            sleep 1
            let i=$i+1
     fi
done
