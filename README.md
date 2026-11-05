# Waywallen Selector

Un selector minimalista de wallpapers para **KDE Plasma y Waywallen**: tarjetas
diagonales en un carrusel flotante, sin panel, botones ni textos sobre las imágenes.

## Características

- Miniaturas estáticas de tu biblioteca existente, sin iniciar nuevos renderizadores.
- Fondos ordenados por fecha de incorporación: del más reciente al más antiguo.
- Navegación con teclado, rueda o arrastre; clic o Enter para aplicar.
- Transición nativa de Waywallen y opción interna de acento automático de Plasma.
- Destino explícito por monitor: nunca se aplica un fondo a todas las pantallas por omisión.
- Ventana de tamaño fijo, compatible con el mosaico de Krohnkite sin recargarlo.
- Instancia única: el mismo atajo abre y cierra el selector.
- No queda ejecutándose después de cerrar la ventana.

Este proyecto es un complemento independiente; **no reemplaza Waywallen**.

## Compatibilidad

- Linux con KDE Plasma 6; comprobado en Wayland.
- **Waywallen 0.5.x instalado mediante Flatpak** (`org.waywallen.waywallen`),
  con el daemon y la integración del escritorio funcionando.
- Biblioteca en `~/.var/app/org.waywallen.waywallen/data/waywallen/waywallen-v2.db`.
- Qt **6.8 o superior**, KDE Frameworks 6, CMake 3.24+, compilador C++20 y Protobuf.

La ruta de la biblioteca y el protocolo están adaptados a esa instalación de
Waywallen. Otras versiones, instalaciones nativas y otros escritorios no están
soportados por esta versión. Las entradas ocultas o cuyos archivos faltan se omiten.

## Compilar e instalar

Dependencias orientativas en Arch Linux/CachyOS:

```bash
sudo pacman -S --needed base-devel cmake qt6-base qt6-declarative qt6-websockets qt6-wayland kconfig protobuf
```

Clona el repositorio y compila:

```bash
git clone https://github.com/riantorres1975/waywallen-selector.git
cd waywallen-selector
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build
kbuildsycoca6 --noincremental
```

La instalación no necesita `sudo`: añade el ejecutable a `~/.local/bin` y los
lanzadores a `~/.local/share/applications` y `~/.local/share/kglobalaccel`.
No sobrescribe configuraciones de KWin, KDE, terminales o Waywallen.
Los archivos de una instalación anterior de **este selector** se reemplazan.

Puedes probarlo sin instalar usando `build/waywallen-selector`.

## Uso

| Acción | Control |
|---|---|
| Abrir/cerrar | **Win + Alt + W** |
| Navegar | **← / →**, rueda o arrastrar |
| Aplicar | **Clic en una imagen** o **Enter** |
| Cerrar sin cambiar el fondo | **Esc** |

Si el atajo no se registra inmediatamente, busca **Waywallen Selector** en
Preferencias del sistema → Teclado → Atajos y asígnale `Meta+Alt+W`.
No reemplaces otro atajo si esa combinación ya está ocupada.

La imagen elegida seguirá siendo un fondo animado cuando Waywallen lo reproduzca;
solo las miniaturas del carrusel son estáticas. El tamaño de la franja se adapta
a la pantalla, hasta 1800 × 460 píxeles lógicos.

## Preferencias internas

Se guardan en `~/.config/local.desktop/WaywallenSelector.conf`:

```ini
[appearance]
transition=1
automaticAccent=true
```

`transition`: `0` sin transición, `1` fundido, `2` barrido, `3` expansión.
La duración es de 500 ms. Estas opciones se aplican **solo al confirmar** un fondo.

`automaticAccent` activa el acento nativo de Plasma y la integración de Waywallen.
No cambia fuentes, iconos, estilos, BTree, bordes personalizados ni los temas de
Kitty/Btop/Vesktop. El acento es global: Plasma decide qué pantalla usa para obtenerlo.
Poner esta preferencia en `false` evita activarlo; no desactiva una opción que ya
se hubiera habilitado en Plasma.

## Comprobaciones sin cambiar el fondo

```bash
build/waywallen-selector --diagnose
build/waywallen-selector --capture /ruta/a/preview.png
```

`--diagnose` consulta la biblioteca, los monitores y la compatibilidad del protocolo.
`--capture` guarda **solo la ventana del selector** y sale. Ambos modos impiden
aplicar wallpapers, incluso si se pulsa Enter o se hace clic en una tarjeta.
La captura puede contener imágenes de tu biblioteca: revísala antes de compartirla.

## Cómo funciona

La biblioteca SQLite se abre en **solo lectura**. El puerto de control del daemon
se obtiene mediante D-Bus y las operaciones se envían por su WebSocket local.
La interfaz se limita al protocolo de Waywallen 0.5.x.

Antes de ajustar la transición se consulta la configuración completa: los campos
globales no modelados y las opciones de los plugins se preservan en protobuf.
No se edita `config.toml` mientras el daemon está ejecutándose. Los ajustes de
sonido, FPS y pausa automática siguen perteneciendo a Waywallen.

No se instalan extensiones ni se modifican permisos de Flatpak. El repositorio
no contiene fondos, credenciales, bases de datos, configuraciones personales ni
archivos compilados.

## Licencia

**GPL-3.0-only**. El esquema de protocolo deriva del proyecto Waywallen;
consulta [THIRD_PARTY.md](THIRD_PARTY.md) y [LICENSE](LICENSE).
