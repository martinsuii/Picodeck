package com.arcademultitool.companion;

interface Link {
    String transport();

    void send(String line) throws Exception;

    void close();
}

interface LineSink {
    void onLine(String line);

    void onClosed(String why);
}
