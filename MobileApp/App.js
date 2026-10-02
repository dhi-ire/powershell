import React, { useState, useEffect } from 'react';
import { StyleSheet, Text, View, ScrollView, ActivityIndicator, TouchableOpacity } from 'react-native';

const BACKEND_URL = 'http://your-backend.com/api';

export default function App() {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  useEffect(() => {
    fetchData();
  }, []);

  const fetchData = async () => {
    setLoading(true);
    setError(null);
    try {
      const response = await fetch(`${BACKEND_URL}/data`);
      if (!response.ok) throw new Error('API Error');
      const json = await response.json();
      setData(json);
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Mobile App</Text>
        <Text style={styles.subtitle}>Connected to Backend</Text>
      </View>

      {loading && <ActivityIndicator size="large" color="#0066cc" style={styles.loader} />}

      {error && (
        <View style={styles.errorBox}>
          <Text style={styles.errorText}>Error: {error}</Text>
        </View>
      )}

      {data && (
        <View style={styles.dataBox}>
          <Text style={styles.dataText}>{JSON.stringify(data, null, 2)}</Text>
        </View>
      )}

      <TouchableOpacity style={styles.button} onPress={fetchData}>
        <Text style={styles.buttonText}>Refresh Data</Text>
      </TouchableOpacity>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
    paddingHorizontal: 20,
  },
  header: {
    marginTop: 40,
    marginBottom: 30,
  },
  title: {
    fontSize: 28,
    fontWeight: 'bold',
    color: '#333',
  },
  subtitle: {
    fontSize: 16,
    color: '#666',
    marginTop: 5,
  },
  loader: {
    marginVertical: 20,
  },
  errorBox: {
    backgroundColor: '#ffebee',
    padding: 15,
    borderRadius: 8,
    marginVertical: 10,
    borderLeftWidth: 4,
    borderLeftColor: '#d32f2f',
  },
  errorText: {
    color: '#d32f2f',
    fontSize: 14,
  },
  dataBox: {
    backgroundColor: '#fff',
    padding: 15,
    borderRadius: 8,
    marginVertical: 15,
    borderWidth: 1,
    borderColor: '#e0e0e0',
  },
  dataText: {
    color: '#333',
    fontSize: 12,
    fontFamily: 'monospace',
  },
  button: {
    backgroundColor: '#0066cc',
    paddingVertical: 12,
    paddingHorizontal: 20,
    borderRadius: 8,
    alignItems: 'center',
    marginVertical: 20,
  },
  buttonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
});
