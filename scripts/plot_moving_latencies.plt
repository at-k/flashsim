set term png enhanced font 'Verdana, 17'
set auto x
#set style data histogram
#set style fill solid 
set grid
#set style histogram cluster gap 1
#set boxwidth 0.9
set key top left

set xtic rotate by -45
set output "plot.png"
set xlabel "# requests"
set ylabel "read latencies percentiles (ms)"

plot filename using 1 with linespoints title '50' fc rgb "blue", "" using 2 with linespoints title "99" fc rgb "green", "" using 3 with linespoints title "99.9" fc rgb "red"
