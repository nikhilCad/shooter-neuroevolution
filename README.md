# Parameter Sweep Results

## Usage

```
make dev    # build and play the game interactively
make sweep  # run this parameter sweep (override with ARGS="--populations=20,40 --hidden=12,24 --generations=4000")
```

Each configuration below trained for 4000 generations. populations=[20,40,60,80] hidden=[12,24,36,48] eliteRatio=0.20 mutationRate=0.15 mutationStrength=0.50

| Population | Hidden | Elites | Generations | Best Fitness | Best Score | Best Time |
|---|---|---|---|---|---|---|
| 20 | 12 | 4 | 4000 | 505.9 | 2400 | 40.3 |
| 20 | 24 | 4 | 4000 | 445.7 | 2200 | 35.0 |
| 20 | 36 | 4 | 4000 | 455.7 | 2200 | 44.3 |
| 20 | 48 | 4 | 4000 | 534.1 | 2500 | 47.0 |
| 40 | 12 | 8 | 4000 | 971.0 | 4200 | 65.3 |
| 40 | 24 | 8 | 4000 | 1191.7 | 5000 | 83.9 |
| 40 | 36 | 8 | 4000 | 602.1 | 2800 | 61.5 |
| 40 | 48 | 8 | 4000 | 421.5 | 2100 | 37.4 |
| 60 | 12 | 12 | 4000 | 864.7 | 3800 | 53.3 |
| 60 | 24 | 12 | 4000 | 1354.6 | 5600 | 99.8 |
| 60 | 36 | 12 | 4000 | 1340.2 | 5500 | 89.3 |
| 60 | 48 | 12 | 4000 | 1073.3 | 4600 | 64.4 |
| 80 | 12 | 16 | 4000 | 1146.0 | 4800 | 79.3 |
| 80 | 24 | 16 | 4000 | 2631.4 | 10400 | 161.2 |
| 80 | 36 | 16 | 4000 | 3735.7 | 14700 | 200.4 |
| 80 | 48 | 16 | 4000 | 2825.1 | 11300 | 145.8 |

## pop20_hidden12

![pop20_hidden12](sweep_images/pop20_hidden12.png)

## pop20_hidden24

![pop20_hidden24](sweep_images/pop20_hidden24.png)

## pop20_hidden36

![pop20_hidden36](sweep_images/pop20_hidden36.png)

## pop20_hidden48

![pop20_hidden48](sweep_images/pop20_hidden48.png)

## pop40_hidden12

![pop40_hidden12](sweep_images/pop40_hidden12.png)

## pop40_hidden24

![pop40_hidden24](sweep_images/pop40_hidden24.png)

## pop40_hidden36

![pop40_hidden36](sweep_images/pop40_hidden36.png)

## pop40_hidden48

![pop40_hidden48](sweep_images/pop40_hidden48.png)

## pop60_hidden12

![pop60_hidden12](sweep_images/pop60_hidden12.png)

## pop60_hidden24

![pop60_hidden24](sweep_images/pop60_hidden24.png)

## pop60_hidden36

![pop60_hidden36](sweep_images/pop60_hidden36.png)

## pop60_hidden48

![pop60_hidden48](sweep_images/pop60_hidden48.png)

## pop80_hidden12

![pop80_hidden12](sweep_images/pop80_hidden12.png)

## pop80_hidden24

![pop80_hidden24](sweep_images/pop80_hidden24.png)

## pop80_hidden36

![pop80_hidden36](sweep_images/pop80_hidden36.png)

## pop80_hidden48

![pop80_hidden48](sweep_images/pop80_hidden48.png)

