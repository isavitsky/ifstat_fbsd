#!/usr/bin/env bash

TRAFFIC="$HOME/traffic"
IFACE=$1
if [[ -z "$IFACE" ]]; then
	echo ∇ n/a ∆ n/a
	exit
fi

NSTAT=`netstat -nbI "$IFACE" | grep '<Link#'| tr -s ' '`

if [[ -z "$NSTAT" ]]; then
	echo ∇ n/a ∆ n/a
	exit
else
	INP=`echo $NSTAT | cut -d' ' -f8`
	OUT=`echo $NSTAT | cut -d' ' -f11`
	if [[ -s "$TRAFFIC" ]]; then
		P_INP=`cut -d' ' -f1 "$TRAFFIC"`
		P_OUT=`cut -d' ' -f2 "$TRAFFIC"`
		echo "$INP $OUT" > "$TRAFFIC"
		SPEED=$(( $INP - $P_INP ))
		(( SPEED *= 8 ))
		echo -n "∇ "
		if [[ $SPEED -lt 1000 ]]; then
			printf "%3d b " $SPEED
		elif [[ $SPEED -lt 1000000 ]]; then
			(( SPEED /= 1000 ))
			printf "%3d k " $SPEED
		elif [[ $SPEED -lt 1000000000 ]]; then
			(( SPEED /= 1000000 ))
			printf "%3d M " $SPEED
		elif [[ $SPEED -lt 1000000000000 ]]; then
			(( SPEED /= 1000000000 ))
			printf "%3d G " $SPEED
		fi
		echo
		SPEED=$(( $OUT - $P_OUT ))
		(( SPEED *= 8 ))
		echo -n "∆ "
		if [[ $SPEED -lt 1000 ]]; then
			printf "%3d b" $SPEED
		elif [[ $SPEED -lt 1000000 ]]; then
			(( SPEED /= 1000 ))
			printf "%3d k" $SPEED
		elif [[ $SPEED -lt 1000000000 ]]; then
			(( SPEED /= 1000000 ))
			printf "%3d M" $SPEED
		elif [[ $SPEED -lt 1000000000000 ]]; then
			(( SPEED /= 1000000000 ))
			printf "%3d G" $SPEED
		fi
	else
		echo "$INP $OUT" > "$TRAFFIC"
		echo ∇ n/a ∆ n/a
	fi
fi
