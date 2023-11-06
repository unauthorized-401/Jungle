from flask import Flask, jsonify, request
from bson.json_util import dumps
from bson.objectid import ObjectId
from pymongo import MongoClient
import jwt
from datetime import datetime


client = MongoClient('localhost', 27017)
db = client.rituday

def register_routes(app):
    def get_user_email(access_token):
        if access_token == None:
            jsonify({'result':'invalid AccessKey'})

        token_withoutBearer = access_token[7:]

        try:
                payload = jwt.decode(token_withoutBearer, app.config['SECRET_KEY'], algorithms="HS256")
                email = payload['email']
                return email
        except jwt.exceptions.DecodeError:
            return jsonify({'message': 'Invalid access token'}), 401
        
    @app.route('/ritual/<string:year>/<string:month>/list', methods=['GET'])
    def rituals_tolist(year, month):
        rituals = list(db.rituals.find({"year":int(year), "month":int(month)}))
        for ritual in rituals:
            name = db.users.find_one({"email":ritual['userEmail']})['name']
            ritual['name'] = name
        
        return jsonify({'result':'success', 'list': dumps(rituals)})

    @app.route('/ritual/<string:year>/<string:month>/<string:day>/list', methods=['GET'])
    def certainDay_rituals_tolist(year, month, day):
        print(day)
        rituals = list(db.rituals.find({"year":int(year), "month":int(month), "day":int(day)}))
        for ritual in rituals:
            name = db.users.find_one({"email":ritual['userEmail']})['name']
            ritual['name'] = name
        
        return jsonify({'result':'success', 'list': dumps(rituals)})

    @app.route('/ritual/<string:ritualId>', methods=['GET'])
    def show_ritual(ritualId):
        IdOfritual = ObjectId(ritualId)
        ritual = db.rituals.find_one({'_id':IdOfritual})
        return jsonify({'result':'success', 'ritual': dumps(ritual)})


    @app.route('/ritual/enrollment', methods=['POST'])
    def enroll_ritual(): 
        access_token = request.headers.get("Authorization")
        userEmail = get_user_email(access_token)    
        print(userEmail)

        date = datetime.now()

        year = date.year
        day = date.day 
        print(type(day))

        month = date.month 
        print(type(month))

        ritual_receive = request.form["ritual_category"]
        content_receive = request.form["ritual_impression"]

        if ritual_receive == "" or content_receive == "":
            return jsonify({'result':'no content'})

        doc = {"category": ritual_receive, "content": content_receive, "year": year ,"day": day, "month": month, "userEmail": userEmail}

        db.rituals.insert_one(doc)

        return jsonify({'result':'success'})


    @app.route('/ritual/<string:ritualId>/update', methods=['POST'])
    def update_ritual(ritualId):
        access_token = request.headers.get("Authorization")
        userEmail = get_user_email(access_token) # 유저 verify
        
        print(ritualId)
        IdOfritual = ObjectId(ritualId)

        newContent = request.form['newContent']

        db.rituals.update_one({'_id':IdOfritual}, {'$set':{'content' : newContent}})    

        return jsonify({'result':'success'})

    @app.route('/ritual/<string:ritualId>/delete', methods=['POST'])
    def delete_ritual(ritualId):
        access_token = request.headers.get("Authorization")
        userEmail = get_user_email(access_token) # 유저 verify

        IdOfritual = ObjectId(ritualId)

        db.rituals.delete_one({'_id': IdOfritual})

        return jsonify({'result':'success'})


