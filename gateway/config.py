from pydantic_settings import BaseSettings, SettingsConfigDict

class Settings(BaseSettings):
    app_name: str = "ZeroLGR Gateway"
    
    # ZeroMQ Config
    zmq_endpoint: str = "tcp://127.0.0.1:5555"
    
    # Stripe Config
    stripe_api_key: str = "sk_test_dummyKey1234567890abcdefg"
    stripe_webhook_secret: str = "whsec_test_secret"
    
    # Accounts used in Ledger
    stripe_escrow_account_id: str = "acc_stripe_escrow"
    merchant_revenue_account_id: str = "acc_merchant_revenue"
    
    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8")

settings = Settings()
