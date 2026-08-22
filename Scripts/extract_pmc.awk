BEGIN { FS=","; n=0 }
{
    # Match lines like: "                    Pmc,     160754,      16744, 100248, 270205, 30373721, 28027164"
    tag=$1; gsub(/^[ \t]+|[ \t]+$/, "", tag);
    if (tag != "Pmc") next;
    tid=$3+0;
    if (tid != TARGET_TID) next;
    n++;
    ts=$2+0; a=$4+0; b=$5+0; c=$6+0; d=$7+0;
    if (n==1) { first_ts=ts; first_a=a; first_b=b; first_c=c; first_d=d; }
    last_ts=ts; last_a=a; last_b=b; last_c=c; last_d=d;
}
END {
    if (n==0) { print "NO_SAMPLES for TID=" TARGET_TID; exit 1; }
    printf "samples=%d\n", n;
    printf "first_ts_us=%d first_LLCMisses=%d first_LLCReference=%d first_InstrRetired=%d first_UnhaltedCycles=%d\n", first_ts, first_a, first_b, first_c, first_d;
    printf "last_ts_us=%d  last_LLCMisses=%d  last_LLCReference=%d  last_InstrRetired=%d  last_UnhaltedCycles=%d\n", last_ts, last_a, last_b, last_c, last_d;
    dt_us = last_ts - first_ts;
    d_llcmiss = last_a - first_a;
    d_llcref  = last_b - first_b;
    d_instr   = last_c - first_c;
    d_cycles  = last_d - first_d;
    printf "delta_window_seconds=%.6f\n", dt_us/1000000.0;
    printf "delta_LLCMisses=%d delta_LLCReference=%d delta_InstrRetired=%d delta_UnhaltedCycles=%d\n", d_llcmiss, d_llcref, d_instr, d_cycles;
    if (d_llcref > 0) printf "LLCMissRatio=%.6f\n", d_llcmiss/d_llcref;
    if (d_cycles > 0) printf "IPC=%.6f\n", d_instr/d_cycles;
}
