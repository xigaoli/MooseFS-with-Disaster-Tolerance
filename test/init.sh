#!/bin/bash

for i in {1..51}
do
    echo $i > ./mfsfrom/gprs_cdr_yyyymmdd$i.txt
done

