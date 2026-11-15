/* clipd ayarları */
#define MAXENT  50          /* geçmiş boyutu */
#define MAXLEN  (1 << 20)   /* bundan büyük girdiler atlanır */
#define LINE    120         /* menüde gösterilen max bayt */
#define RECLAIM 1           /* sahip uygulama kapanınca panoyu geri al */
#define DMENU   { "dmenu", "-i", "-l", "10", "-p", "clip", NULL }
