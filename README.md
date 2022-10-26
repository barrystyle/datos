pacprotocol staging tree 18.x
=============================



What is PAC Protocol?
---------------------

PAC Protocol is built on the foundation of a first-of-its-kind blockchain technology; utilizing both proof-of-stake (PoSv3, environmentally friendly) in addition to deterministic masternodes - to offer a large globally decentralized network.

Other features include a custom second layer token environment, in addition to the digital architecture required for decentralized data storage, content management and more using IPFS software - all the while using the most recent cutting-edge release of Dash.



How do I build the software?
----------------------------

The most troublefree and reproducable method of building the repository is via the depends method:

    git clone https://github.com/pacprotocol/pacprotocol
    cd pacprotocol/depends
    make HOST=x86_64-pc-linux-gnu
    cd ..
    ./autogen.sh
    CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure
    make



License
-------

pacprotocol is released under the terms of the MIT license. See [COPYING](COPYING) for more information or see https://opensource.org/licenses/MIT.



Development Process
-------------------

The `18.x` branch is meant to be stable. Development is normally done in separate branches. [Tags](https://github.com/pacprotocol/pacprotocol/tags) are created to indicate new official, stable release versions of pacprotocol.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).



