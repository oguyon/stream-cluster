#!/usr/bin/env bash

# BENCHMARKS


MKSEQEXEC="../build/image-cluster-mktxtseq"
RNUCLEXEC="../build/image-cluster"
CLPLOT="../build/image-cluster-plot"

NBSAMPLE=1000000
RLIM="0.10"

OPTIONS="-maxim $NBSAMPLE -outdir clusteroutdir"

# SHORT TERM MEMORY FOR SLOW-MOVING POINT

# Slow moving point on spiral
# Demonstrates short-term memory, testing last cluster first
# approximately 1.00 dist/frame
# Should be ~1 dist/frame
# > Validates short-term memory performance gain
#
$MKSEQEXEC $NBSAMPLE 2Dspiral.txt 2Dspiral
$RNUCLEXEC $RLIM $OPTIONS 2Dspiral.txt
$CLPLOT 2Dspiral.clustered.txt -png




# TRIANGULATION / GEOMETRY

# ON CURVE

# Random point on circle
# Demonstrates geometric solving
# approximately 2.73 dist/frame
# In 2D, we expect ~2.5, first 1 give 2 solution, then 50% prob next one gets it right (2 dists) otherwise 3 dists
# > Validates short-term memory gain
#
$MKSEQEXEC $NBSAMPLE 2Dcircle-shuffle.txt 2Dcircle -shuffle
$RNUCLEXEC $RLIM $OPTIONS 2Dcircle-shuffle.txt
$CLPLOT 2Dcircle-shuffle.clustered.txt -png

# Random points on spiral
# Demonstrates geometric gain
# r=0.1  3.01 dist/frame
# r=0.05 2.95 dist/frame
# r=0.02 3.03 dist/frame
# r=0.02 3.03 dist/frame
# In 2D, we expect ~3, first 1 give finite numb solution, then 2nd one nails it, and 3rd one is good
#
$MKSEQEXEC $NBSAMPLE 2Dspiral-shuffle.txt 2Dspiral -shuffle
$RNUCLEXEC $RLIM $OPTIONS 2Dspiral-shuffle.txt
$CLPLOT 2Dspiral-shuffle.clustered.txt -png


# gprob option to learn the fine geometrical structure
# 2.52 dist/frame
# 
$RNUCLEXEC $RLIM -maxim $NBSAMPLE -gprob -fmatcha 1.0 -fmatchb 0.0 2Dspiral-shuffle.txt




# ON RANDOM POINTS

RLIM="0.10"

# Random pt in 2D
# Expects > 3.2134
# one -> thick arc, 2 -> square, 3 -> OK prob is PI/4, otherwise needs 1+ more dist
# r=0.10 3.53 dist/frame
# r=0.05 3.96 dist/frame
#
$MKSEQEXEC $NBSAMPLE 2Drand.txt 2Drand
$RNUCLEXEC $RLIM $OPTIONS 2Drand.txt
$CLPLOT 2Drand.clustered.txt -png



# Random pt in 3D
# Expects > ~5+
# one -> thick sphere, 2 -> thick ring, 3 -> two cubes, 
# 4 -> 50% off, 50% good side
# if good side, OK prob is PI/6, otherwise 1+ more needed
# r=0.10 9.78 dist/frame
# 3000 clusters
#
$MKSEQEXEC $NBSAMPLE 3Drand.txt 3Drand
$RNUCLEXEC $RLIM -maxcl 10000 $OPTIONS 3Drand.txt
$CLPLOT 3Drand.clustered.txt -png



# RECURRING SEQUENCE
#
# 3.00
#
$MKSEQEXEC $NBSAMPLE 2DcircleP10n.txt 2Dcircle10 -noise 0.04
$RNUCLEXEC $RLIM $OPTIONS 2DcircleP10n.txt
$CLPLOT 2DcircleP10n.clustered.txt -png

# 1.0 
#
$RNUCLEXEC $RLIM -maxcl 10000 -pred[10,100,1] -maxim $NBSAMPLE 2DcircleP10n.txt







