datos staging tree 18.x
=======================



What is Datos?
--------------

Datos sets the standard for cutting-edge masternode technology, being the first to combine proof of stake consensus with deterministic masternodes, along with distributed high-speed storage, allowing users to be compensated for storage space they provide to the network.



How do I build the software?
----------------------------

The most troublefree and reproducable method of building the repository is via the depends method:

    git clone https://github.com/datosdrive/datos
    cd datos/depends
    make HOST=x86_64-pc-linux-gnu
    cd ..
    ./autogen.sh
    CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure
    make



License
-------

datos is released under the terms of the MIT license. See [COPYING](COPYING) for more information or see https://opensource.org/licenses/MIT.



Development Process
-------------------

The `18.x` branch is meant to be stable. Development is normally done in separate branches. [Tags](https://github.com/datosdrive/datos/tags) are created to indicate new official, stable release versions of datos.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).
