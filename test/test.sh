#!/bin/bash
     for n in {1..100}
     do
          dd if=/dev/zero of=/mnt/mfsfrom/$n.txt bs=1k count=1 &> /dev/null
     done
