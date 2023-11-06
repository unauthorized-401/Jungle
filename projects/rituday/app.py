from flask import Flask, render_template, jsonify, request
import requests
from bs4 import BeautifulSoup
from pymongo import MongoClient
from bson.objectid import ObjectId
import datetime
import jwt
from werkzeug.security import generate_password_hash, check_password_hash
import random
from api.ritual import register_routes
from config import configure_secret_key
import string

app = Flask(__name__)

app.jinja_env.add_extension('jinja2.ext.do')
register_routes(app)
configure_secret_key(app)

client = MongoClient('localhost', 27017)
#client = MongoClient('mongodb://jiwon:jiwon@54.180.31.95', 27017)

db = client.rituday

@app.route('/')
def home():
    return render_template('calendar.html')

@app.route('/login.html')
def login():
    return render_template('login.html')

@app.route('/membership.html')
def membership():
    return render_template('membership.html')

@app.route('/find.html')
def find():
    return render_template('find.html')

@app.route('/change.html')
def change():
    return render_template('change.html')

@app.route('/calendar.html')
def calendar():
    return render_template('calendar.html')

@app.route('/account/login', methods=['POST'])
def account_login():
    # Get data from client
    receive_id = request.form['give_id']
    receive_password = request.form['give_password']

    # Make user
    user = {'id': receive_id}

    # Find user
    get_user = db.users.find_one(user)

    # Check user
    check_user = db.users.count_documents(user)

    # Return data
    if check_user > 0:
        # Check password
        if (check_password_hash(get_user['password'], receive_password) == False):
            return jsonify({'result': 'fail'})
    
        # Check token
        token = jwt.encode({'email' : get_user['email'], 'name' : get_user['name']},
                            app.config['SECRET_KEY'],
                            algorithm='HS256')
        

        return jsonify({'result': 'success', 'token': token})
    else:
        return jsonify({'result': 'fail'})
    
@app.route('/account/check', methods=['GET'])
def check_account():
    # Get data from client
    token_receive = request.form['token']
    try:
        payload = jwt.decode(token_receive, 
                         app.config['SECRET_KEY'],
                         algorithms=['HS256'])
        print(payload)
        # Return data
        return jsonify({'result': 'success'})
    except jwt.ExpiredSignatureError:
        return jsonify({'result': 'fail'})
    except jwt.exceptions.DecodeError:
        return jsonify({'result': 'fail'})
    
@app.route('/account/check/id', methods=['POST'])
def check_id():
    # Get data from client
    receive_id = request.form['give_id']

    # Make user
    user = {'id': receive_id}

    # Check email
    check_user = db.users.count_documents(user)

    # Return data
    if check_user > 0:
        return jsonify({'result': 'fail'})
    else:
        return jsonify({'result': 'success'})
  
@app.route('/account/check/email', methods=['POST'])
def check_email():
    # Get data from client
    receive_email = request.form['give_email']

    # Make user
    user = {'email': receive_email}

    # Check email
    check_user = db.users.count_documents(user)

    # Return data
    if check_user > 0:
        return jsonify({'result': 'fail'})
    else:
        return jsonify({'result': 'success'})
    
@app.route('/account/create', methods=['POST'])
def create_account():
    # Get data from client
    receive_id = request.form['give_id']
    receive_password = request.form['give_password']
    receive_name = request.form['give_name']
    receive_email = request.form['give_email']

    # Password Encryption
    encryption = generate_password_hash(receive_password)

    # Make user
    user = {'id': receive_id, 'password': encryption, 'name': receive_name, 'email': receive_email}

    # Create user
    db.users.insert_one(user)

    # Return data
    return jsonify({'result': 'success'})

@app.route('/account/find/id', methods=['POST'])
def find_id():
    # Get data from client
    receive_name = request.form['give_name']
    receive_email = request.form['give_email']

    # Make user
    user = {'name': receive_name, 'email': receive_email}

    # Check email
    check_user = db.users.count_documents(user)

    # Return data
    if check_user > 0:
        get_user = db.users.find_one(user)
        id = get_user['id']
        return jsonify({'result': 'success', 'id': id})
        
    else:
        return jsonify({'result': 'fail'})

@app.route('/account/find/password', methods=['POST'])
def find_password():
    # Get data from client
    receive_id = request.form['give_id']
    receive_email = request.form['give_email']

    # Make user
    user = {'id': receive_id, 'email': receive_email}

    # Check email
    check_user = db.users.count_documents(user)

    # Return data
    if check_user > 0:
        get_user = db.users.find_one(user)
        temporary_password = ''

        # Get temporary password
        for i in range(8):
            temporary_password += str(random.choice(string.ascii_uppercase + string.digits))

        # Password Encryption
        encryption_password = generate_password_hash(temporary_password)

        db.users.update_one(user, {'$set':{"password": encryption_password}})
        return jsonify({'result': 'success', 'password': temporary_password})
        
    else:
        return jsonify({'result': 'fail'})

@app.route('/account/change/password', methods=['POST'])
def change_password():
    # Get data from client
    receive_id = request.form['give_id']
    receive_password = request.form['give_password']

    # Password Encryption
    encryption = generate_password_hash(receive_password)

    # Change Password
    db.users.update_one({"id": receive_id}, {'$set':{"password": encryption}})

    # Return data
    return jsonify({'result': 'success'})
    
if __name__ == '__main__':  
   app.run('0.0.0.0',port=5000,debug=True)