export const API_CONFIG = {
  BACKEND_URL: process.env.BACKEND_URL || 'http://your-backend.com/api',
  TIMEOUT: 30000,
  HEADERS: {
    'Content-Type': 'application/json',
  },
};

export const createApiClient = () => {
  return {
    get: async (endpoint) => {
      const response = await fetch(`${API_CONFIG.BACKEND_URL}${endpoint}`, {
        method: 'GET',
        headers: API_CONFIG.HEADERS,
      });
      return response.json();
    },
    post: async (endpoint, data) => {
      const response = await fetch(`${API_CONFIG.BACKEND_URL}${endpoint}`, {
        method: 'POST',
        headers: API_CONFIG.HEADERS,
        body: JSON.stringify(data),
      });
      return response.json();
    },
  };
};
