import { useEffect, useRef } from 'react';
import { useMetricsStore } from '../store/metricsStore';

const WS_URL = 'ws://localhost:8000/ws/metrics';

export const useWebSocket = () => {
  const ws = useRef<WebSocket | null>(null);
  const onMetricsSample = useMetricsStore((state) => state.onMetricsSample);
  const onSagaEvent = useMetricsStore((state) => state.onSagaEvent);

  useEffect(() => {
    const connect = () => {
      ws.current = new WebSocket(WS_URL);

      ws.current.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.type === 'METRICS') {
            onMetricsSample(data);
          } else if (data.type === 'SAGA_EVENT') {
            onSagaEvent(data);
          }
        } catch (e) {
          console.error('Failed to parse WS message', e);
        }
      };

      ws.current.onclose = () => {
        setTimeout(connect, 2000); // Reconnect loop
      };
    };

    connect();

    return () => {
      if (ws.current) {
        ws.current.onclose = null;
        ws.current.close();
      }
    };
  }, [onMetricsSample, onSagaEvent]);

  return ws.current;
};
