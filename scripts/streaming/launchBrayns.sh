#! /bin/bash
DIRECTORY=`dirname $0`
export PATH=$DIRECTORY:$PATH
srun -p opendeck --account proj3 --ntasks-per-node 4 -N 4 -c 4 --constraint=volta --gres=gpu:1 --oversubscribe --multi-prog $DIRECTORY/multi_brayns.conf
