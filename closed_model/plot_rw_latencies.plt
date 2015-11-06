set term png enhanced font 'Verdana, 17'
set auto x
set style data histogram
set style fill solid 
set grid
set style histogram cluster gap 1
set boxwidth 0.9
set key top left

set output "read_write.png"
set xlabel "# concurrent requests"
set ylabel "read latency (ms)"

plot "data_file_90.dat" using 3:xtic(1) title '50^{th} %ile' fc rgb "blue", "" using 7 title "99^{th} %ile" fc rgb "green", "" using 9 title "99.9^{th} %ile" fc rgb "red"
