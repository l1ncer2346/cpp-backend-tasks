# Grafana panels

`dashboard.json` adds three panels backed by the metrics from `web_exporter.py`:

1. HTTP request and response rates;
2. network errors split into separate `read`, `write`, and `accept` series;
3. p95 and average response latency.

The error query intentionally keeps the `where` label instead of aggregating
with `sum`, so each network operation is visible as its own graph.
