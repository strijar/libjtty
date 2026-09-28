# Run from jtty: gnuplot tests/sensitivity/plot.gp
set terminal svg size 1000,600 dynamic font 'DejaVu Sans,12'
set output 'tests/sensitivity/sensitivity.svg'
set title 'JTTY: decoding probability in white noise'
set xlabel 'SNR in 2500 Hz, dB'
set ylabel 'Correctly decoded frames, %'
set xrange [-19.5:-9.5]
set yrange [0:104]
set xtics 1
set ytics 10
set grid
set key bottom right
set arrow 1 from graph 0, first 50 to graph 1, first 50 nohead dashtype 2 lc rgb '#888888'
set arrow 2 from graph 0, first 90 to graph 1, first 90 nohead dashtype 2 lc rgb '#888888'
set label 1 '500 frames per point; identical PCM; search 950–1050 Hz; 95% Wilson CI' at screen 0.5,0.025 center font ',10'
set bmargin 5
plot 'tests/sensitivity/curve.dat' using 1:($2*100):($3*100):($4*100) with yerrorlines lw 2 pt 7 lc rgb '#1675b6' title 'C / WAVA', \
     '' using 1:($5*100):($6*100):($7*100) with yerrorlines lw 2 pt 5 lc rgb '#da7024' title 'WSJT-X / production receiver'
set terminal pngcairo size 1400,840 font 'DejaVu Sans,14'
set output 'tests/sensitivity/sensitivity.png'
replot
