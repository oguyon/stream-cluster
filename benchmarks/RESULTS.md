# Overview

This file compiles tests and benchmarks for the gric-cluster program, with a short discussion of results in comments.



# Simple 2D patterns


## 2D spiral

In this first test, a 2D point slowly oves outward in a spiral pattern.

```
./gric-mktxtseq 20000 2Dspiral.txt 2Dspiral
# OUTPUT:
# 2Dspiral.txt
```




### txt output (2D)
```
./gric-cluster [options] 0.1 2Dspiral.txt
# OUTPUT:

```




### Image output 

Write 2D spot to stream, with cnt2sync:
```
gric-ascii-spot-2-video 256 0.1 2Dspiral.txt spot2d -isio -cnt2sync
```

Then run clustering:
```
gric-cluster 0.1 -stream -cmt2sync spot2d
```

0.1 2600
0.2 2370
0.4 1450
0.8  520
1.0  350
1.2  250



# BENCHMARKS
NBSAMPLE=10000
MAXNBCL=1000


NOOUTPUTOPT="-o -no_dcc -o -no_tm -o -no_clustered -o -no_anchors -o -no_counts -o -no_membership -o -no_clustered -o -no_clusters"

NOOUTPUTOPT="-no_dcc -no_tm -no_clustered -no_anchors -no_counts -no_membership -no_clustered -no_clusters"


# TESTING STREAMING
CLUSTEREXEC="/home/oguyon/src/stream-cluster/build/gric-cluster"

$CLUSTEREXEC 50.0 $NOOUTPUTOPT -maxim $NBSAMPLE -maxcl $MAXNBCL -stream earth.cb1024

exit


# Aiming at ~1000 clusters, ~1e6 samples


# TXT INPUT ANALYSIS
# CHECKING AGAINST EXPECTED



# MP4 INPUT - BENCHMARKING SPEED

# Size 64x64

#./benchmarks.bash $NOOUTPUTOPT -maxcl $MAXNBCL -maxim $NBSAMPLE -p 2Dspiral -n $NBSAMPLE -t mp4 -r 125

# 10000  mp4 file size = 545K
# Result: Time=2342.577ms, Dists=454790, Clusters=942

# 100000  mp4 file size = 3.6M
# Result: Time=5023.886ms, Dists=607504, Clusters=1006, Mem=193084KB

# 1000000  mp4 file size = 29M
# Result: Time=27367.844ms, Dists=1498507, Clusters=997, Mem=413416KB

# BIRCH

./benchmarks.bash $NOOUTPUTOPT -maxcl $MAXNBCL -maxim $NBSAMPLE -p 2Dspiral -n $NBSAMPLE -t mp4 -r 45 -a birch

# 10000  mp4 file size = 545K
# Result: Time=2242.07ms, Dists=N/A, Clusters=1003

# 100000  mp4 file size = 3.6M
# Result: Time=18697.53ms, Dists=N/A, Clusters=986, Mem=19794508KB

# 1000000  mp4 file size = 29M


exit










MKSEQEXEC="../build/gric-mktxtseq"
RNUCLEXEC="../build/gric-cluster"
CLPLOT="../build/gric-plot"

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
$CLPLOT 2Dspiral.txt clusteroutdir/cluster_run.log




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
$CLPLOT 2Dcircle-shuffle.txt clusteroutdir/cluster_run.log

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
$CLPLOT 2Dspiral-shuffle.txt clusteroutdir/cluster_run.log


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
$CLPLOT 2Drand.txt clusteroutdir/cluster_run.log



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
$CLPLOT 3Drand.txt clusteroutdir/cluster_run.log



# RECURRING SEQUENCE
#
# 3.00
#
$MKSEQEXEC $NBSAMPLE 2DcircleP10n.txt 2Dcircle10 -noise 0.04
$RNUCLEXEC $RLIM $OPTIONS 2DcircleP10n.txt
$CLPLOT 2DcircleP10n.txt clusteroutdir/cluster_run.log

# 1.0 
#
$RNUCLEXEC $RLIM -maxcl 10000 -pred[10,100,1] -maxim $NBSAMPLE 2DcircleP10n.txt