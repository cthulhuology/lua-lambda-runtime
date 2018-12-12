
function dump(t)
	for k,v in pairs(t) do
		print(k .. "=" .. v )
	end
end

function handler(request,context)
	print("request" .. request )
	dump(context)
	return "hello world","this is an error";
end
