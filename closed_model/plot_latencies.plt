set term png enhanced font 'Verdana, 17'
set auto x
set style data histogram
set style fill solid 
set grid
set style histogram cluster gap 1
set boxwidth 0.9
set key top left

set output "util_90.png"
set xlabel "# threads"
set ylabel "99^{th} percentile latency (ms)"

plot "data_file_90.dat" using 6:xtic(1) title 'Read Only' fc rgb "blue", "" using 7 title "Read Write" fc rgb "red"
